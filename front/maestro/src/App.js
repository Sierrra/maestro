import React from 'react';
import Button from 'react-bootstrap/Button';
import Jumbotron from 'react-bootstrap/Jumbotron';
import Container from 'react-bootstrap/Container';
import Row from 'react-bootstrap/Row';
import Col from 'react-bootstrap/Col';
import Spinner from 'react-bootstrap/Spinner';
import './App.css';
import axios from 'axios';
import {Navbar, Nav}  from "react-bootstrap";
import {Table} from "react-bootstrap";
import {
    BrowserRouter as Router,
    Switch,
    Route,
    Link
  } from "react-router-dom";

navigator.getUserMedia = (navigator.getUserMedia
    || navigator.webkitGetUserMedia
    || navigator.mozGetUserMedia
    || navigator.msGetUserMedia)

class App extends React.Component {
  constructor(props){
    super(props);
    this.state = {
      loading: false,
      blobURL: '',
      isBlocked: false,
      selectedFile: null,
        data: null,
        noData: '',
        urlList: [],
    };
  }

    onClickDownloadHandler = param => e => {
        axios.get("https://maestro.fun/audio?id="+param)
    }
  componentDidMount() {
    navigator.getUserMedia({ audio: true },
        () => {
          console.log('Permission Granted');
          this.setState({ isBlocked: false });
        },
        () => {
          console.log('Permission Denied');
          this.setState({ isBlocked: true })
        },
    );
  }

    onFilesChange=event=>{
        this.setState({
            selectedFile: event.target.files[0],
            loaded: 0,
        })
        console.log(event.target.files[0])
    }

    onClickUploadHandler = () => {
        const data = new FormData()
        data.append('file', this.state.selectedFile)
        this.setState({loading: true})
        axios.post("https://maestro.fun/get_similar?n=5", data, { headers: { 'Content-Type': 'multipart/form-data' }} )
            .then(res => {
                this.setState({loading: false})
                if(!res.data){
                    this.setState({noData: 'nothing found'});
                    console.log('nothing found');

                }
                else{
                    console.log(res.status);
                    this.setState({ data: res.data, urlList:[], noData: '' })
                }
            }).catch(error => {
                this.setState({loading:false})
                this.setState({noData: 'nothing found'});
                console.log(error);
            })
    }

  render(){
    return (
        <div>
            <Navbar bg="light" expand="lg">
                <Navbar.Brand href="/">Maestro</Navbar.Brand>
                <Navbar.Toggle aria-controls="basic-navbar-nav" />
                <Navbar.Collapse id="basic-navbar-nav">
                    <Nav className="mr-auto">
                        <Nav.Link as={Link} to="/">Home</Nav.Link>
                        <Nav.Link as={Link} to="/about">About</Nav.Link>
                    </Nav>
                </Navbar.Collapse>
            </Navbar>
            <Switch>
            <Route exact path="/">
            <Container>
                <Row>
                    <Col>You can upload file</Col>

                </Row>
                <Row>
                    <Col>
                        <Jumbotron>
                            <input type="file" name="file" accept="audio/wav" onChange={this.onFilesChange}/>
                            {this.state.loading ? 
                            <Button variant="primary" disabled>
                            <Spinner
                              as="span"
                              animation="grow"
                              size="sm"
                              role="status"
                              aria-hidden="true"
                            />
                            Loading...
                          </Button> : <Button onClick={this.onClickUploadHandler} >Upload</Button>
                            }
                            
                        </Jumbotron>
                    </Col>
                </Row>
                <Row>
                    <Col>
                        <Table striped bordered hover size="sm">
                                <thead>
                                <tr>
                                    <th>#</th>
                                    <th>Composer</th>
                                    <th>Title</th>
                                    <th>Link</th>
                                </tr>
                                </thead>
                                <tbody>
                                { this.state.data ? this.state.data.map((val, index) =>
                                        <tr><td>{index+1}</td>
                                        <td>{val.Composer}</td>
                                        <td>{val.Title}</td>
                                        <td><a href={"https://maestro.fun/audio?id="+val.Index} download> download</a></td>
                                    </tr>
                                        ) : <tr><td>empty</td></tr>}
                                </tbody>
                                </Table>

                    </Col>
                </Row>
            </Container>
            </Route>
            <Route path="/about">
                <h2>So fast, so impressive classical music search</h2>
                <h2>The project contains all kinds of problems</h2>
            </Route>
            </Switch>
        </div>
    );
  }
}

export default App;
